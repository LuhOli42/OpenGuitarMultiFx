#include "BbdDelayProcessor.h"
#include "Effects/IconKit.h"
#include "Effects/PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;
    using Role = BbdDelayProcessor::Role;

    struct KnobSpec
    {
        const char* id;
        const char* name;
        Role role;
        float defaultValue;
        double taperMid = 0.5;
        int positions = 0;
        std::vector<const char*> labels {};
    };

    struct ModelSpec
    {
        const char* prefix;
        const char* name;
        std::vector<KnobSpec> knobs;
    };

    const ModelSpec& specFor (DelayModel m)
    {
        static const ModelSpec dm2 { "dm2", "DM-2",
            { { "rate", "Repeat Rate", Role::time, 0.5f }, { "echo", "Echo", Role::level, 0.5f },
              { "intensity", "Intensity", Role::repeat, 0.4f } } };
        static const ModelSpec dmm { "dmm", "Deluxe Memory Man",
            { { "level", "Level", Role::preampLevel, 0.5f, 0.15 }, { "blend", "Blend", Role::blend, 0.5f },
              { "feedback", "Feedback", Role::repeat, 0.4f }, { "chorus", "Chorus-Vibrato", Role::modDepth, 0.0f },
              { "delay", "Delay", Role::time, 0.5f },
              { "lfo", "Mod", Role::modSwitch, 0.0f, 0.5, 2, { "Chorus", "Vibrato" } } } };
        static const ModelSpec mxr { "mxr118", "M118 Analog Delay",
            { { "delay", "Delay", Role::time, 0.5f }, { "mix", "Mix", Role::blend, 0.5f },
              { "regen", "Regen", Role::repeat, 0.4f } } };
        static const ModelSpec ad9 { "ad9", "AD9",
            { { "time", "Delay Time", Role::time, 0.5f, 0.85 }, { "level", "Delay Level", Role::level, 0.5f },
              { "repeat", "Repeat", Role::repeat, 0.4f } } };
        static const ModelSpec ad80 { "ad80", "AD80",
            { { "repeat", "Repeat", Role::repeat, 0.15f, 0.85 }, { "blend", "Blend", Role::blend, 0.5f },
              { "time", "Delay Time", Role::time, 0.5f, 0.15 } } };
        static const ModelSpec fx90 { "fx90", "FX90",
            { { "repeat", "Repeat", Role::repeat, 0.4f }, { "mix", "Mix", Role::blend, 0.5f },
              { "delay", "Delay", Role::time, 0.5f } } };
        static const ModelSpec sad1 { "sad1", "SAD-1",
            { { "delay", "Delay", Role::time, 0.5f }, { "repeat", "Repeat", Role::repeat, 0.15f, 0.85 },
              { "depth", "Depth", Role::level, 0.5f, 0.85 } } };
        static const ModelSpec dd2 { "dd2", "DD-2",
            { { "elevel", "E.Level", Role::level, 0.5f }, { "fback", "F.Back", Role::repeat, 0.4f },
              { "dtime", "D.Time", Role::time, 0.5f, 0.15 },
              { "mode", "Mode", Role::mode, 2.0f, 0.5, 4, { "S", "M", "L", "Hold" } } } };
        switch (m)
        {
            case DelayModel::dm2: return dm2;
            case DelayModel::memoryMan: return dmm;
            case DelayModel::mxr118: return mxr;
            case DelayModel::ad9: return ad9;
            case DelayModel::ad80: return ad80;
            case DelayModel::fx90: return fx90;
            case DelayModel::sad1: return sad1;
            case DelayModel::dd2: return dd2;
        }
        return dm2;
    }

    Rational shelfUp (double ra, double rb, double c) { return { { 1.0, c * (ra + rb) }, { 1.0, c * ra } }; }
    Rational shelfDown (double ra, double rb, double c) { return { { 1.0, c * ra }, { 1.0, c * (ra + rb) } }; }
    // The Boss/Ibanez/Arion pre-emphasis: -(47K || 100p) / (47K || (10K + 6.8n)), undone by the matching feedback.
    Rational bossPre() { return inverting (parallelRC (47.0e3, 100.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9))); }
    Rational bossDe() { return cascade (gain (-1.0), inverting (parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9)), capacitor (100.0e-12)), resistor (47.0e3))); }

    double magnitudeAt (const Rational& h, double hz) { return std::abs (h.at (Complex (0.0, 2.0 * juce::MathConstants<double>::pi * hz))); }

    /** Two Sallen-Key sections in cascade (each with its own follower) as one BBD filter. */
    Rational twoStages (const std::vector<LadderNode>& a, const std::vector<LadderNode>& b)
    {
        auto ha = ladderTransfer (a), hb = ladderTransfer (b);
        auto h = cascade (ha, hb);
        h.scale = std::sqrt (ha.scale * hb.scale);
        return h;
    }
    std::vector<LadderNode> sk (double r, double cOut, double cGnd) { return { { r, 0.0, 0.0, cOut, 0.0 }, { r, 0.0, cGnd, 0.0, 0.0 } }; }
    std::vector<LadderNode> sk3 (double r, double c1, double c2Out, double c3) { return { { r, 0.0, c1, 0.0, 0.0 }, { r, 0.0, 0.0, c2Out, 0.0 }, { r, 0.0, c3, 0.0, 0.0 } }; }

    BucketBrigadeDelay::Filter toBbd (const Rational& h)
    {
        const auto pz = poleZero (h);
        BucketBrigadeDelay::Filter f;
        f.poles = pz.poles;
        f.zeros = pz.zeros;
        f.dcGain = pz.dcGain;
        return f;
    }
}

BbdDelayProcessor::BbdDelayProcessor (DelayModel m) : model (m)
{
    const auto& spec = specFor (model);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (spec.prefix, spec.name, "|");
    for (const auto& k : spec.knobs)
    {
        const juce::String id = juce::String (spec.prefix) + "_" + k.id;
        std::unique_ptr<juce::AudioParameterFloat> p;
        if (k.positions >= 2)
        {
            auto labels = k.labels;
            p = std::make_unique<juce::AudioParameterFloat> (id, k.name, juce::NormalisableRange<float> (0.0f, (float) (k.positions - 1), 1.0f), k.defaultValue,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
                { return juce::String (labels[(size_t) juce::jlimit (0, (int) labels.size() - 1, juce::roundToInt (v))]); }));
        }
        else
            p = std::make_unique<juce::AudioParameterFloat> (id, k.name, juce::NormalisableRange<float> (0.0f, 1.0f), k.defaultValue);
        knobs.push_back ({ k.role, p.get(), k.taperMid });
        parameters->addChild (std::move (p));
    }
}

const char* BbdDelayProcessor::getName() const { return specFor (model).name; }

float BbdDelayProcessor::knob (Role role, float fallback) const noexcept
{
    for (const auto& k : knobs)
        if (k.role == role)
            return k.param->get();
    return fallback;
}

double BbdDelayProcessor::knobTapered (Role role, float fallback) const noexcept
{
    for (const auto& k : knobs)
        if (k.role == role)
            return pots::law (k.param->get(), k.taperMid);
    return fallback;
}

void BbdDelayProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    Rational preamp = gain (1.0), input = gain (1.0), dry = gain (1.0), wet = gain (1.0), regen = couplingHighpass (1.0e-6, 47.0e3);
    Rational bbdIn, bbdOut;
    double warp = 2340.0;
    hasPreamp = stereo = false;
    dryFromInput = model == DelayModel::memoryMan || model == DelayModel::mxr118 || model == DelayModel::ad80
                   || model == DelayModel::fx90 || model == DelayModel::dd2; // dry tapped before the pre-emphasis
    railVolts = 3.5;
    double quant = 0.0;

    switch (model)
    {
        case DelayModel::dm2:
            input = cascade (couplingHighpass (0.047e-6, 470.0e3), bossPre());
            dry = bossDe();
            wet = bossDe();
            bbdIn = ladderTransfer (sk3 (10.0e3, 6.8e-9, 82.0e-9, 330.0e-12));                      // R14-R16, C11/C13/C14
            bbdOut = twoStages (sk3 (10.0e3, 2.2e-9, 33.0e-9, 1.0e-9), sk (10.0e3, 39.0e-9, 330.0e-12)); // Q3, Q4
            stages = 4096; minDelay = 0.020; maxDelay = 0.300; loopMax = 1.08;
            break;
        case DelayModel::memoryMan:
            // 15 V. Preamp: 100K in, 22K + Level (1M) || 27p feedback -- up to x10, into the rails.
            hasPreamp = true;
            preamp = couplingHighpass (0.1e-6, 100.0e3);
            input = gain (1.0);
            bbdIn = twoStages (sk (16.0e3, 33.0e-9, 2.7e-9), sk (33.0e3, 2.7e-9, 1.0e-9));
            bbdOut = twoStages (sk (16.0e3, 2.7e-9, 2.7e-9), sk (24.3e3, 4.7e-9, 2.7e-9));
            stages = 8192; minDelay = 0.030; maxDelay = 0.550; loopMax = 1.08;
            railVolts = 6.5; preampRail = 6.5;
            break;
        case DelayModel::mxr118:
            input = couplingHighpass (0.01e-6, 470.0e3);
            bbdIn = ladderTransfer (sk3 (10.0e3, 1.6e-9, 2.2e-9, 470.0e-12));
            bbdOut = twoStages (sk (10.0e3, 750.0e-12, 470.0e-12), sk (10.0e3, 1.0e-9, 300.0e-12));
            stages = 3072; minDelay = 0.030; maxDelay = 0.300; loopMax = 1.0;
            railVolts = 5.0;
            break;
        case DelayModel::ad9:
            input = cascade (couplingHighpass (0.047e-6, 510.0e3), bossPre());
            dry = bossDe(); wet = bossDe();
            bbdIn = ladderTransfer (sk3 (10.0e3, 10.0e-9, 82.0e-9, 330.0e-12));
            bbdOut = twoStages (sk3 (10.0e3, 2.2e-9, 39.0e-9, 1.0e-9), sk (10.0e3, 39.0e-9, 820.0e-12));
            stages = 4096; minDelay = 0.010; maxDelay = 0.300; loopMax = 1.04;
            break;
        case DelayModel::ad80:
            input = cascade (couplingHighpass (0.047e-6, 510.0e3), shelfUp (1.0e3, 10.0e3, 15.0e-9));
            dry = gain (1.0);
            wet = shelfDown (1.0e3, 10.0e3, 15.0e-9);
            bbdIn = ladderTransfer (sk3 (10.0e3, 6.0e-9, 6.0e-9, 3.3e-9));
            bbdOut = twoStages (sk3 (10.0e3, 3.3e-9, 3.3e-9, 1.8e-9), sk3 (10.0e3, 2.2e-9, 2.2e-9, 1.2e-9));
            stages = 4096; minDelay = 0.015; maxDelay = 0.330; loopMax = 1.0;
            break;
        case DelayModel::fx90:
            input = cascade (couplingHighpass (0.01e-6, 470.0e3), shelfUp (3.3e3, 18.0e3, 33.0e-9));
            dry = gain (1.0);
            wet = shelfDown (3.3e3, 18.0e3, 33.0e-9);
            bbdIn = ladderTransfer (sk3 (27.0e3, 1.8e-9, 8.2e-9, 470.0e-12));
            bbdOut = twoStages (sk3 (27.0e3, 2.2e-9, 6.8e-9, 470.0e-12), sk3 (27.0e3, 2.2e-9, 6.8e-9, 470.0e-12));
            stages = 4096; minDelay = 0.020; maxDelay = 0.400; loopMax = 1.0;
            break;
        case DelayModel::sad1:
            input = cascade (couplingHighpass (0.033e-6, 470.0e3), bossPre());
            dry = bossDe(); wet = bossDe();
            bbdIn = ladderTransfer (sk3 (10.0e3, 2.2e-9, 47.0e-9, 330.0e-12));
            bbdOut = ladderTransfer (sk3 (10.0e3, 2.2e-9, 33.0e-9, 1.0e-9));
            stages = 4096; minDelay = 0.020; maxDelay = 0.300; loopMax = 1.0;
            stereo = true;
            break;
        case DelayModel::dd2:
            // Pre-emphasis 47K in, 10K + 4.7n, 33K || 33p; three 7 kHz Sallen-Key sections in, two out.
            input = cascade (couplingHighpass (0.027e-6, 1.0e6),
                             inverting (parallelRC (33.0e3, 33.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 4.7e-9))));
            dry = gain (1.0);
            wet = cascade (gain (-1.0), inverting (parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 4.7e-9)), capacitor (33.0e-12)), resistor (33.0e3)));
            {
                auto s1 = ladderTransfer (sk (10.0e3, 4.7e-9, 2.7e-9)), s2 = ladderTransfer (sk (10.0e3, 6.8e-9, 470.0e-12));
                auto s3 = ladderTransfer (sk (10.0e3, 18.0e-9, 220.0e-12));
                bbdIn = cascade (cascade (s1, s2), s3);
                bbdIn.scale = std::cbrt (s1.scale * s2.scale * s3.scale);
            }
            bbdOut = twoStages (sk (10.0e3, 2.7e-9, 1.2e-9), sk (10.0e3, 6.8e-9, 150.0e-12));
            stages = 2 * 16384; minDelay = 0.200; maxDelay = 0.800; loopMax = 1.0; // L; S and M use 1/16 and 1/4
            quant = 2.0 * railVolts / 4096.0; // 12 bits across the converter's swing
            warp = 3000.0;
            break;
    }

    // Wet level: Echo/Level at full puts the repeats at the dry signal's level (1 kHz), as these mixers are built to.
    // Normalised at the wet path's passband peak (30 Hz - 3 kHz), not at one frequency: these filters corner anywhere
    // from ~1 to 7 kHz, and normalising where one of them already rolls off would boost everything below it.
    double chain = 0.0;
    for (double hz = 30.0; hz < 3000.0; hz *= 1.05)
        chain = std::max (chain, magnitudeAt (bbdIn, hz) * magnitudeAt (bbdOut, hz) * magnitudeAt (wet, hz)
                                     * (dryFromInput ? magnitudeAt (input, hz) : 1.0) / std::max (1.0e-12, magnitudeAt (dry, hz)));
    // The wet always passes the pre-emphasis; the dry passes it only where it is tapped after it (then the dry
    // mixer's de-emphasis undoes it) -- so for a dry tapped before it, the emphasis belongs on the wet side of the sum.
    wetNorm = 1.0 / std::max (1.0e-12, chain);
    // Repeat: the loop's peak gain (filters' resonance included) is the knob times loopMax -- above 1 at full on the
    // pedals that self-oscillate (DM-2, Memory Man, AD9), bounded by the rails like the real thing.
    double loopPeak = 0.0;
    for (double hz = 30.0; hz < 20000.0; hz *= 1.05)
        loopPeak = std::max (loopPeak, magnitudeAt (regen, hz) * magnitudeAt (bbdIn, hz) * magnitudeAt (bbdOut, hz));
    regenNorm = 1.0 / std::max (1.0e-12, loopPeak);

    const auto inFilter = toBbd (bbdIn), outFilter = toBbd (bbdOut);
    for (auto& ch : channels)
    {
        ch.preamp.prepare (preamp, newSampleRate, 0.0);
        ch.input.prepare (input, newSampleRate, warp);
        ch.dryMix.prepare (dry, newSampleRate, warp);
        ch.wetMix.prepare (wet, newSampleRate, warp);
        ch.regenFilter.prepare (regen, newSampleRate, 0.0);
        ch.line.prepare (newSampleRate, stages, inFilter, outFilter);
        ch.line.setQuantization (quant);
        if (model == DelayModel::dd2)
            for (int k = 0; k < 2; ++k)
            {
                ch.shortLines[(size_t) k].prepare (newSampleRate, k == 0 ? stages / 16 : stages / 4, inFilter, outFilter);
                ch.shortLines[(size_t) k].setQuantization (quant);
            }
        ch.compressor.prepare (newSampleRate, 1.0e-6, 0.3);
        ch.expander.prepare (newSampleRate, 1.0e-6, 0.3);
    }
    smoothedTime.reset (newSampleRate, 0.08); // a Delay knob is turned, not stepped: the pitch bend of a real BBD
    smoothedTime.setCurrentAndTargetValue ((float) knobTapered (Role::time, 0.5f));
    lfo.setThresholds (-1.0, 1.0);
    reset();
}

void BbdDelayProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.preamp.reset();
        ch.input.reset();
        ch.dryMix.reset();
        ch.wetMix.reset();
        ch.regenFilter.reset();
        ch.line.reset();
        for (auto& l : ch.shortLines)
            l.reset();
        ch.compressor.reset();
        ch.expander.reset();
        ch.lastRaw = 0.0;
    }
    lfo.reset (0.0, true);
    shortcut.reset();
}

double BbdDelayProcessor::processSample (Channel& ch, double x, double clockHz, double repeat, double wetLevel, double dryLevel,
                                         double preGain, bool hold) noexcept
{
    double in = x;
    if (hasPreamp)
        in = bbdchorus::railClip (preGain * ch.preamp.process (x), -preampRail, preampRail, 0.5);
    const double pre = bbdchorus::railClip (ch.input.process (in), -railVolts, railVolts);
    const double squeezed = bbdchorus::railClip (ch.compressor.compress (pre), -railVolts, railVolts);
    // Regeneration re-enters in the compressed domain (from the BBD's own output, before the expander).
    const double fed = bbdchorus::railClip (squeezed + repeat * regenNorm * ch.regenFilter.process (ch.lastRaw), -railVolts, railVolts);
    auto& line = (model == DelayModel::dd2 && lineIndex < 2) ? ch.shortLines[(size_t) lineIndex] : ch.line;
    line.setFrozen (hold);
    const double raw = line.process (fed, clockHz);
    ch.lastRaw = raw;
    const double wet = bbdchorus::railClip (ch.expander.expand (raw), -railVolts, railVolts); // the expander's op-amp has rails too
    return dryLevel * ch.dryMix.process (dryFromInput ? in : pre) + wetLevel * wetNorm * ch.wetMix.process (wet);
}

void BbdDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // DD-2 modes: the same converter rate range (20.5-82 kHz) over a quarter / the whole / 1/16 of its memory.
    int mode = juce::roundToInt (knob (Role::mode, 2.0f));
    double lo = minDelay, hi = maxDelay;
    double memoryFraction = 1.0;
    lineIndex = 2;
    if (model == DelayModel::dd2)
    {
        if (mode == 0) { lo = 0.0125; hi = 0.050; memoryFraction = 1.0 / 16.0; lineIndex = 0; }
        else if (mode == 1) { lo = 0.050; hi = 0.200; memoryFraction = 0.25; lineIndex = 1; }
    }
    const bool hold = model == DelayModel::dd2 && mode == 3;

    smoothedTime.setTargetValue ((float) knobTapered (Role::time, 0.5f));
    const double repeat = loopMax * knobTapered (Role::repeat, 0.4f);
    double wetLevel = knobTapered (Role::level, 1.0f), dryLevel = 1.0;
    if (knob (Role::blend, -1.0f) >= 0.0f) // Blend / Mix pots: dry one way, delay the other, both full at centre
    {
        const double b = knob (Role::blend, 0.5f);
        dryLevel = std::min (1.0, 2.0 * (1.0 - b));
        wetLevel = std::min (1.0, 2.0 * b);
    }
    const double preGain = hasPreamp ? (22.0e3 + 1.0e6 * knobTapered (Role::preampLevel, 0.5f)) / 100.0e3 : 1.0;

    // Memory Man chorus / vibrato: an LFO on the 4047's timing node (slow for chorus, fast for vibrato).
    const double modDepth = knob (Role::modDepth, 0.0f);
    const double modHz = knob (Role::modSwitch, 0.0f) >= 0.5f ? 4.5 : 0.45;
    lfo.setSlopes (4.0 * modHz, 4.0 * modHz);

    const int solve = stereo ? 1 : shortcut.begin (channels, buffer, sampleRate);
    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = smoothedTime.getNextValue();
        lastDelay = lo + (hi - lo) * t; // period proportional to the Delay pot's resistance
        double effStages = stages * memoryFraction;
        if (modDepth > 0.0)
            lastDelay *= 1.0 + 0.012 * modDepth * lfo.advance (dt);
        const double clockForQueue = 0.5 * effStages / lastDelay; // S / M run their own, shorter queue at the same rate

        if (stereo)
        {
            const double x = right != nullptr ? 0.5 * ((double) left[i] + (double) right[i]) : (double) left[i];
            auto& ch = channels[0];
            const double pre = bbdchorus::railClip (ch.input.process (x), -railVolts, railVolts);
            const double squeezed = bbdchorus::railClip (ch.compressor.compress (pre), -railVolts, railVolts);
            const double fed = bbdchorus::railClip (squeezed + repeat * regenNorm * ch.regenFilter.process (ch.lastRaw), -railVolts, railVolts);
            const double raw = ch.line.process (fed, clockForQueue);
            ch.lastRaw = raw;
            const double wetSig = wetLevel * wetNorm * ch.wetMix.process (bbdchorus::railClip (ch.expander.expand (raw), -railVolts, railVolts));
            const double drySig = ch.dryMix.process (pre);
            left[i] = (float) (drySig + wetSig);
            if (right != nullptr)
                right[i] = (float) (drySig - wetSig); // OUT 2: the wet through the inverting mixer
        }
        else
        {
            for (int c = 0; c < solve; ++c)
            {
                auto* data = buffer.getWritePointer (c);
                data[i] = (float) processSample (channels[(size_t) c], (double) data[i], clockForQueue, repeat, wetLevel, dryLevel, preGain, hold);
            }
        }
    }

    if (stereo)
    {
        for (int c = 2; c < numChannels; ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
    }
    else
        shortcut.end (buffer);
}

void BbdDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> analog = icon::loadSvg (IconData::analog_delay_svg, IconData::analog_delay_svgSize);
    static const std::unique_ptr<juce::Drawable> digital = icon::loadSvg (IconData::digital_delay_svg, IconData::digital_delay_svgSize);
    icon::drawSvg (g, b, model == DelayModel::dd2 ? digital.get() : analog.get());
}

} // namespace openguitarmultifx
