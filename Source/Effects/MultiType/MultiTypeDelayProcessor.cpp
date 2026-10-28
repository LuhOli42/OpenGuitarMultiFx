#include "MultiTypeDelayProcessor.h"
#include "MultiTypeIcons.h"
#include <IconData.h>

namespace openguitarmultifx
{
using namespace mt;

namespace
{
    constexpr double kMaxSeconds = 8.0;
    using A = MultiTypeDelayProcessor::Algo;
}

// Type list for the common Delay families; the values are this engine's own presets.
const std::vector<MultiTypeDelayProcessor::TypeSpec>& MultiTypeDelayProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                 algo        time  ratio fb   fbR  cross mix  lowCut highCut drive bits l1Rate l1Dep l2Rate l2Dep diff dTime comp duck sweep motor head2
        { "Digital Mono",       A::mono,     500, 100, 35,  35,  0,  30,  20,  20000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "Digital Stereo",     A::stereo,   500, 75,  35,  35,  0,  30,  20,  20000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "Analog Mono",        A::mono,     380, 100, 40,  40,  0,  30,  120, 2800,  3,   0,  0.6,  8,   0.15, 3,   0,  50,  40,  0,  0,   100, 50 },
        { "Analog Stereo",      A::stereo,   380, 85,  40,  40,  0,  30,  120, 2800,  3,   0,  0.6,  8,   0.15, 3,   0,  50,  40,  0,  0,   100, 50 },
        { "Mono Tape",          A::tape,     420, 100, 35,  15,  0,  30,  150, 4500,  4,   0,  0.9,  6,   5.5,  2,   0,  50,  0,   0,  0,   100, 50 },
        { "Stereo Tape",        A::stereo,   420, 90,  35,  35,  0,  30,  150, 4500,  4,   0,  0.9,  6,   5.5,  2,   0,  50,  0,   0,  0,   100, 50 },
        { "Ping-Pong",          A::pingPong, 375, 100, 45,  45,  0,  30,  20,  12000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "Dual Delay",         A::dual,     500, 66,  30,  30,  20, 30,  20,  12000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "Reverse Delay",      A::reverse,  600, 100, 20,  20,  0,  50,  20,  12000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "Sweep Delay",        A::stereo,   400, 80,  45,  45,  0,  35,  20,  16000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   0,  1,   100, 50 },
        { "Ducking Delay",      A::stereo,   450, 80,  45,  45,  0,  40,  20,  14000, 0,   0,  0.5,  0,   0.3,  0,   0,  50,  0,   18, 0,   100, 50 },
        { "Vintage Digital",    A::mono,     400, 100, 35,  35,  0,  30,  60,  7000,  0,   12, 0.5,  0,   0.3,  0,   0,  50,  0,   0,  0,   100, 50 },
        { "2290 w/ Modulation", A::stereo,   500, 90,  35,  35,  0,  30,  60,  12000, 0,   0,  0.8,  18,  0.2,  6,   0,  50,  0,   0,  0,   100, 50 },
        { "Ambient Stereo",     A::stereo,   700, 66,  55,  55,  0,  35,  150, 8000,  0,   0,  0.3,  10,  0.1,  5,   70, 70,  0,   0,  0,   100, 50 },
        { "Deluxe Mind Guy",    A::mono,     450, 100, 45,  45,  0,  35,  120, 2500,  4,   0,  0.5,  12,  0.2,  2,   0,  50,  60,  0,  0,   100, 50 },
        { "Mono BBD",           A::mono,     300, 100, 40,  40,  0,  30,  150, 3500,  3,   0,  0.6,  6,   0.2,  2,   0,  50,  30,  0,  0,   100, 50 },
        { "Stereo BBD",         A::stereo,   300, 85,  40,  40,  0,  30,  150, 3500,  3,   0,  0.6,  6,   0.2,  2,   0,  50,  30,  0,  0,   100, 50 },
        { "Lo-Fi Tape",         A::tape,     380, 100, 40,  10,  0,  35,  300, 2500,  6,   6,  1.2,  15,  7.0,  6,   0,  50,  0,   0,  0,   100, 60 },
        { "Stereo Mind Guy",    A::stereo,   450, 80,  45,  45,  0,  35,  120, 2500,  4,   0,  0.5,  12,  0.2,  2,   0,  50,  60,  0,  0,   100, 50 },
        { "Diffused Delay",     A::stereo,   550, 75,  50,  50,  0,  35,  80,  9000,  0,   0,  0.3,  4,   0.2,  2,   85, 80,  0,   0,  0,   100, 50 },
        { "Graphite Copy",      A::mono,     420, 100, 40,  40,  0,  30,  150, 3000,  3,   0,  0.4,  10,  0.2,  2,   0,  50,  40,  0,  0,   100, 50 },
        { "Worn Tape",          A::tape,     450, 100, 40,  20,  0,  35,  250, 3000,  5,   0,  1.0,  12,  6.5,  4,   0,  50,  0,   0,  0,   100, 70 },
    };
    return t;
}

MultiTypeDelayProcessor::MultiTypeDelayProcessor (int fixedTypeIndex)
    : MultiTypeDelayProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypeDelayProcessor::MultiTypeDelayProcessor()
{
    juce::StringArray typeNames;
    for (const auto& t : types()) typeNames.add (t.name);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypeDelay", "Multi-Type Delay", "|");
    auto add = [] (juce::AudioProcessorParameterGroup& g, std::unique_ptr<juce::AudioParameterFloat> p) { auto* r = p.get(); g.addChild (std::move (p)); return r; };

    type = add (*parameters, choiceParam ("type", "Type", typeNames, 0));
    time = add (*parameters, floatParam ("time", "Time", 1, (float) kMaxSeconds * 1000.0f - 50.0f, 500, 0.4f, "ms", 0));
    feedback = add (*parameters, floatParam ("feedback", "Feedback", -100, 100, 35, 1.0f, "%", 0));
    mix = add (*parameters, floatParam ("wet", "Wet", 0, 100, 60, 1.0f, "%", 0));
    killDry = add (*parameters, choiceParam ("killDry", "Kill Dry", { "Off", "On" }, 0));
    // The old Mix kept the dry full up to 50 % with the wet at 2x Mix: Wet = 2x Mix is the same sound.
    registerLegacyMix (mix, killDry, "mix", LegacyMixLaw::centreFull, 100.0);
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));

    auto* cfg = parameters.get(); // one page: every control is a direct child
    ratio = add (*cfg, floatParam ("ratio", "L/R Time Ratio", 1, 100, 100, 1.0f, "%", 0));
    spread = add (*cfg, floatParam ("spread", "Spread", -100, 100, 100, 1.0f, "%", 0));
    feedbackR = add (*cfg, floatParam ("feedbackR", "Feedback R / Fb 2", -100, 100, 35, 1.0f, "%", 0));
    crossFeedback = add (*cfg, floatParam ("cross", "Cross Feedback", 0, 100, 0, 1.0f, "%", 0));
    masterFeedback = add (*cfg, floatParam ("masterFeedback", "Master Feedback", 0, 200, 100, 1.0f, "%", 0));
    drive = add (*cfg, floatParam ("drive", "Drive", 0, 10, 0));
    bitReduction = add (*cfg, floatParam ("bitReduction", "Bit Reduction", 0, 20, 0, 1.0f, {}, 0));
    phaseReverse = add (*cfg, choiceParam ("phaseReverse", "Phase Reverse", { "NONE", "LEFT", "RIGHT", "BOTH" }, 0));
    inputGain = add (*cfg, floatParam ("inputGain", "Input Gain", 0, 100, 100, 1.0f, "%", 0));
    motorSpeed = add (*cfg, floatParam ("motor", "Motor Speed", 50, 200, 100, 1.0f, "%", 0));
    head2Ratio = add (*cfg, floatParam ("head2", "Head 2 Ratio", 0, 100, 50, 1.0f, "%", 0));
    head2Level = add (*cfg, floatParam ("head2Level", "Head 2 Level", 0, 100, 0, 1.0f, "%", 0));
    echoPan = add (*cfg, floatParam ("echoPan", "Echo Pan", -100, 100, 0, 1.0f, "%", 0));

    auto* eq = parameters.get(); // one page: every control is a direct child
    lowCut = add (*eq, floatParam ("lowCut", "Low Cut", 10, 2000, 20, 0.4f, "Hz", 0));
    highCut = add (*eq, floatParam ("highCut", "High Cut", 200, 20000, 20000, 0.4f, "Hz", 0));
    eq1Freq = add (*eq, floatParam ("eq1Freq", "Freq 1", 20, 2000, 300, 0.4f, "Hz", 0));
    eq1Gain = add (*eq, floatParam ("eq1Gain", "Gain 1", -12, 12, 0, 1.0f, "dB"));
    eq2Freq = add (*eq, floatParam ("eq2Freq", "Freq 2", 100, 10000, 2500, 0.4f, "Hz", 0));
    eq2Gain = add (*eq, floatParam ("eq2Gain", "Gain 2", -12, 12, 0, 1.0f, "dB"));

    auto* mod = parameters.get(); // one page: every control is a direct child
    lfo1Type = add (*mod, choiceParam ("lfo1Type", "LFO1 Type", lfoShapeNames(), 0));
    lfo1Rate = add (*mod, floatParam ("lfo1Rate", "LFO1 Rate", 0.05f, 10, 0.5f, 0.5f, "Hz", 2));
    lfo1Depth = add (*mod, floatParam ("lfo1Depth", "LFO1 Depth", 0, 100, 0, 1.0f, "%", 0));
    lfo2Type = add (*mod, choiceParam ("lfo2Type", "LFO2 Type", lfoShapeNames(), 0));
    lfo2Rate = add (*mod, floatParam ("lfo2Rate", "LFO2 Rate", 0.05f, 10, 0.3f, 0.5f, "Hz", 2));
    lfo2Depth = add (*mod, floatParam ("lfo2Depth", "LFO2 Depth", 0, 100, 0, 1.0f, "%", 0));
    lfoPhase = add (*mod, floatParam ("lfoPhase", "LFO Phase", 0, 180, 90, 1.0f, "deg", 0));
    depthRange = add (*mod, choiceParam ("depthRange", "Depth Range", { "LOW", "HIGH" }, 0));
    diffusion = add (*mod, floatParam ("diffusion", "Diffusion", 0, 100, 0, 1.0f, "%", 0));
    diffusionTime = add (*mod, floatParam ("diffusionTime", "Diffusion Time", 0, 100, 50, 1.0f, "%", 0));
    compander = add (*mod, floatParam ("compander", "Compander", 0, 100, 0, 1.0f, "%", 0));
    duckAtten = add (*mod, floatParam ("duckAtten", "Ducker Atten", 0, 40, 0, 1.0f, "dB"));
    duckThreshold = add (*mod, floatParam ("duckThreshold", "Ducker Threshold", -60, 0, -30, 1.0f, "dB"));
    duckRelease = add (*mod, floatParam ("duckRelease", "Ducker Release", 10, 1000, 200, 0.5f, "ms", 0));
    sweepStart = add (*mod, floatParam ("sweepStart", "Sweep Start", 100, 2000, 300, 0.4f, "Hz", 0));
    sweepStop = add (*mod, floatParam ("sweepStop", "Sweep Stop", 500, 8000, 3000, 0.4f, "Hz", 0));
    sweepRate = add (*mod, floatParam ("sweepRate", "Sweep Rate", 0.05f, 5, 0.4f, 0.5f, "Hz", 2));
    sweepQ = add (*mod, floatParam ("sweepQ", "Resonance", 0.5f, 8, 2, 0.5f, {}, 1));

    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (0);
}

void MultiTypeDelayProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (time, t.time); setParam (ratio, t.ratio); setParam (feedback, t.feedback); setParam (feedbackR, t.feedbackR);
    setParam (crossFeedback, t.cross); setParam (mix, juce::jmin (100.0f, 2.0f * t.mix)); /* the table's Mix column, as Wet */ setParam (lowCut, t.lowCut); setParam (highCut, t.highCut);
    setParam (drive, t.drive); setParam (bitReduction, t.bits);
    setParam (lfo1Rate, t.lfo1Rate); setParam (lfo1Depth, t.lfo1Depth); setParam (lfo2Rate, t.lfo2Rate); setParam (lfo2Depth, t.lfo2Depth);
    setParam (diffusion, t.diffusion); setParam (diffusionTime, t.diffTime); setParam (compander, t.compander);
    setParam (duckAtten, t.duckAtten); setParam (motorSpeed, t.motor); setParam (head2Ratio, t.head2);
    setParam (head2Level, t.algo == Algo::tape ? 60.0f : 0.0f);
    setParam (masterFeedback, 100); setParam (spread, 100); setParam (eq1Gain, 0); setParam (eq2Gain, 0);
    setParam (phaseReverse, 0); setParam (echoPan, 0); setParam (inputGain, 100); setParam (level, 0);
}

void MultiTypeDelayProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state);
    watcher.syncWithoutApplying();
}

void MultiTypeDelayProcessor::prepare (double sampleRateHz, int, int)
{
    watcher.poll();
    fs = sampleRateHz;
    const int n = (int) (kMaxSeconds * fs) + 16;
    for (auto& l : line) l.prepare (n);
    for (auto& r : revBuf) r.assign ((size_t) (kMaxSeconds * fs) + 16, 0.0f);
    for (auto& c : diff) for (auto& d : c) d.prepare ((int) (0.05 * fs));
    smoothTime.reset (fs, 0.25); // a moving time knob glides like a real delay
    smoothTime.setCurrentAndTargetValue (time->get());
    mixLevel.prepare (fs);
    reset();
}

void MultiTypeDelayProcessor::reset()
{
    for (auto& l : line) l.reset();
    for (auto& r : revBuf) std::fill (r.begin(), r.end(), 0.0f);
    for (auto& c : diff) for (auto& d : c) d.reset();
    for (int c = 0; c < 2; ++c) { fbLow[c].reset(); fbHigh[c].reset(); fbEq1[c].reset(); fbEq2[c].reset(); sweepFilter[c].reset(); }
    duckEnv = 0; compEnv[0] = compEnv[1] = 1e-3; fbState[0] = fbState[1] = 0;
    revWrite = 0; revPhase = 0;
}

void MultiTypeDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const auto& spec = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))];
    const auto algo = spec.algo;
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    smoothTime.setTargetValue (time->get());
    mixLevel.setTargets (mix->get(), level->get(), killDry->get() > 0.5f);
    const double master = masterFeedback->get() / 100.0;
    const double fbL = feedback->get() / 100.0 * master, fbR = feedbackR->get() / 100.0 * master, cross = crossFeedback->get() / 100.0 * master;
    const double r = ratio->get() / 100.0, sp = spread->get() / 100.0, inG = inputGain->get() / 100.0;
    const double driveG = 0.25 + drive->get() * 0.6;
    const double bits = bitReduction->get() > 0.5f ? std::pow (2.0, 23.0 - bitReduction->get()) : 0.0;
    const double diffMix = diffusion->get() / 100.0, comp = compander->get() / 100.0;
    const double rangeMs = asIndex (depthRange) == 0 ? 2.0 : 10.0;
    const double d1 = lfo1Depth->get() / 100.0 * rangeMs * 1e-3 * fs, d2 = lfo2Depth->get() / 100.0 * rangeMs * 1e-3 * fs;
    const double phaseOff = lfoPhase->get() / 360.0;
    const auto s1 = (LfoShape) asIndex (lfo1Type), s2 = (LfoShape) asIndex (lfo2Type);
    const int pr = asIndex (phaseReverse);
    const double motor = motorSpeed->get() / 100.0, h2 = head2Ratio->get() / 100.0, h2Level = head2Level->get() / 100.0;
    const double pan = echoPan->get() / 100.0;
    const double duckThr = juce::Decibels::decibelsToGain ((double) duckThreshold->get());
    const double duckDepth = juce::Decibels::decibelsToGain (-(double) duckAtten->get());
    const double duckRel = std::exp (-1.0 / (duckRelease->get() * 1e-3 * fs));
    const double compCoef = std::exp (-1.0 / (0.02 * fs));
    lfo1.set (lfo1Rate->get(), fs);
    lfo2.set (lfo2Rate->get(), fs);
    sweepLfo.set (sweepRate->get(), fs);
    for (int c = 0; c < 2; ++c)
    {
        fbLow[c].set (lowCut->get(), fs);
        fbHigh[c].set (highCut->get(), fs);
        fbEq1[c].peak (eq1Freq->get(), fs, eq1Gain->get(), 0.7);
        fbEq2[c].peak (eq2Freq->get(), fs, eq2Gain->get(), 0.7);
        const int dl = (int) ((5.0 + diffusionTime->get() * 0.3) * 1e-3 * fs);
        diff[c][0].setLength (dl + c * 37); diff[c][0].g = 0.6;
        diff[c][1].setLength ((int) (dl * 0.61) + c * 23); diff[c][1].g = 0.6;
    }

    auto feedbackPath = [&] (int c, double s) {
        s = fbLow[c].hp (s);
        s = fbHigh[c].lp (s);
        s = fbEq2[c].process (fbEq1[c].process (s));
        if (drive->get() > 0.01f) s = std::tanh (s * driveG) / driveG;
        if (bits > 0.0) s = std::round (s * bits) / bits;
        if (diffMix > 0.0) s = (1.0 - diffMix) * s + diffMix * diff[c][1].process (diff[c][0].process ((float) s));
        return s;
    };

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : L[i];
        const double inMono = 0.5 * (inL + inR) * inG;
        const double peak = std::max (std::abs (inL), std::abs (inR));
        duckEnv = peak > duckEnv ? peak : duckEnv * duckRel;
        const double duck = duckEnv > duckThr ? duckDepth : 1.0;

        const double t = smoothTime.getNextValue() * 1e-3 * fs;
        const double m1L = lfo1.next (s1), m1R = lfo1.next (s1, phaseOff), m2 = lfo2.next (s2);
        lfo1.advance(); lfo2.advance();
        double wetL = 0, wetR = 0;

        switch (algo)
        {
            case Algo::mono:
            {
                const double y = line[0].read (t + d1 * m1L + d2 * m2);
                line[0].push ((float) (inMono + fbL * feedbackPath (0, y)));
                wetL = y * (1.0 - std::max (0.0, pan));
                wetR = y * (1.0 + std::min (0.0, pan));
                break;
            }
            case Algo::stereo:
            case Algo::dual:
            {
                const double yL = line[0].read (t + d1 * m1L + d2 * m2);
                const double yR = line[1].read (t * r + d1 * m1R + d2 * m2);
                const double fL = feedbackPath (0, yL), fR = feedbackPath (1, yR);
                line[0].push ((float) (inL * inG + fbL * fL + cross * fR));
                line[1].push ((float) (inR * inG + fbR * fR + cross * fL));
                wetL = yL; wetR = yR;
                break;
            }
            case Algo::pingPong:
            {
                const double yL = line[0].read (t + d1 * m1L + d2 * m2);
                const double yR = line[1].read (t * r + d1 * m1R + d2 * m2);
                line[0].push ((float) (inMono + fbL * feedbackPath (1, yR)));
                line[1].push ((float) (fbL * feedbackPath (0, yL)));
                wetL = yL; wetR = yR;
                break;
            }
            case Algo::tape:
            {
                // Two playback heads on one loop; motor speed scales both distances; wow/flutter from the LFOs.
                const double td = t / motor + d1 * m1L + d2 * m2;
                const double y1 = line[0].read (td);
                const double y2 = line[0].read (std::max (8.0, td * h2));
                line[0].push ((float) (inMono + feedbackPath (0, fbL * y1 + fbR * y2)));
                const double y = y1 + h2Level * y2;
                wetL = y * (1.0 - std::max (0.0, pan));
                wetR = y * (1.0 + std::min (0.0, pan));
                break;
            }
            case Algo::reverse:
            {
                // Two backward-reading grains half a segment apart with sin^2 windows (they sum to one).
                const int len = (int) revBuf[0].size();
                const double seg = std::max (64.0, t);
                revBuf[0][(size_t) revWrite] = (float) (inMono + fbL * fbState[0]);
                revPhase += 1.0 / seg;
                if (revPhase >= 1.0) revPhase -= 1.0;
                double y = 0;
                for (int g = 0; g < 2; ++g)
                {
                    double p = revPhase + 0.5 * g;
                    p -= std::floor (p);
                    const double back = p * 2.0 * seg;
                    const double w = std::sin (kPi * p) * std::sin (kPi * p);
                    double rp = revWrite - back;
                    while (rp < 0) rp += len;
                    const int i0 = (int) rp;
                    const double fr = rp - i0;
                    y += w * (revBuf[0][(size_t) i0] * (1.0 - fr) + revBuf[0][(size_t) ((i0 + 1) % len)] * fr);
                }
                revWrite = (revWrite + 1) % len;
                fbState[0] = feedbackPath (0, y);
                wetL = wetR = y;
                break;
            }
        }

        // compander: the wet level breathes with its own envelope (BBD-style)
        if (comp > 0.0)
        {
            for (int c = 0; c < 2; ++c)
            {
                double& e = compEnv[c];
                const double a = std::abs (c == 0 ? wetL : wetR);
                e = std::max (a, e * compCoef + a * (1.0 - compCoef));
                const double g = std::pow (std::max (e, 1e-4) / 0.3, comp * 0.5);
                (c == 0 ? wetL : wetR) *= juce::jlimit (0.05, 2.0, g);
            }
        }
        if (spec.sweep > 0.0f)
        {
            const double lv = 0.5 + 0.5 * sweepLfo.next (LfoShape::sine);
            sweepLfo.advance();
            const double f = sweepStart->get() * std::pow (sweepStop->get() / sweepStart->get(), lv);
            sweepFilter[0].bandpass (f, fs, sweepQ->get());
            sweepFilter[1].bandpass (f * 1.1, fs, sweepQ->get());
            wetL = sweepFilter[0].process (wetL) * 2.0;
            wetR = sweepFilter[1].process (wetR) * 2.0;
        }
        if (pr == 1 || pr == 3) wetL = -wetL;
        if (pr == 2 || pr == 3) wetR = -wetR;
        const double mid = 0.5 * (wetL + wetR), side = 0.5 * (wetL - wetR) * sp;
        wetL = (mid + side) * duck;
        wetR = (mid - side) * duck;

        const double m = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        const double dryG = mixLevel.dry.getNextValue(), wetG = m;
        L[i] = (float) ((inL * dryG + wetL * wetG) * g);
        if (R != nullptr) R[i] = (float) ((inR * dryG + wetR * wetG) * g);
        else L[i] = (float) ((inL * dryG + 0.5 * (wetL + wetR) * wetG) * g);
    }
}

void MultiTypeDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    mt::drawIconByKey (g, b, mt::delayIconKey (types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))].name));
}

} // namespace openguitarmultifx
